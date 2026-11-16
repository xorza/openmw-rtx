# Lamp shadow rays at the Census and Excise Office — 2026-11-13

A high-end NVIDIA laptop GPU, release, 2560x1440: the corner of Seyda Neen's Census and Excise Office a player reported,
a plastered wall lit from the left beside a door frame, at 09:10, vanilla and the Fullrest Repack alike.

## What the frame shows

The denoised frame holds blotches 10 to 20 pixels across over the darker half of the wall, where the mean of 16 frames
and the converged reference are smooth; as blotchy with the bounce off and without the upscaler. The noise is the lamps'
direct light.

## How often the held lamp's ray got through

The lamps' light alone and unfiltered is two-valued: a pixel whose held lamp's ray got through shows the lamps' whole
unshadowed sum, one whose ray was stopped next to nothing. Over the dark band one pixel in 94 carried all of the light,
over the lit wall one in 3.6: the lamps the one reservoir weighed most were stopped before they reached the wall. The
lamps' draw in two strata followed (`2026-11-13-lamp-strata.md`).

## What `noise` missed

`noise` called the place as clean as its bar: its figures are over the whole frame, which the smooth wall and the dark
door frame fill, and the band is a seventh of its width. A local fault needs its region read, as `noise`'s noisiest
tile (`worstTile`) reads one.
