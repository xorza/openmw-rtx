# Several bounce rays a pixel — 2026-11-01

`[RTX] bounce rays` (`--bounce-rays`): the eye's surface traces `N` bounce rays a frame, each in a direction of its own
and worth one `N`-th of the bounce. Release, the `default` and `interiors` suites at 1920x1080 traced at 1280x720,
vanilla, on a high-end NVIDIA laptop GPU, a low-end AMD integrated GPU (Windows) and an AMD handheld (RADV, its clock
held).

## Cost

**Each ray costs what the first one does, on all three cards**: the `bounce` and `landing` zones double with each
doubling of the count, to within 6%, and the frame grows by what they do. On the desk one ray is 1.0–1.3 ms of the two
zones at the ship and the guild, eight rays 8.7–11 ms; on the handheld and the integrated GPU one ray is already 31–56
ms of a 84–165 ms frame. The slider is for a card with room to spare.

## Noise

- **The denoised and upscaled frame barely shows the rays**: `noise --ab=bounce-rays=1,N` strafing and walking moved the
  mean noise by 0.02 at most between one and four rays, bias and fireflies likewise. The denoiser's history already
  averages the bounce over many frames.
- **The raw frame does**, with the filter and the upscaler off: the guild's noise 4.16 → 3.51 from one ray to eight and
  its fireflies, which are the bounce's, 2.31 → 0.59 in a thousand. The noise levels out at the part that is not the
  bounce: the lamps' and the sun's draws.

## A deeper path, refused

A bounce off the bounce's landing, ending at the last landing with a throughput floor: the second step cost about what
the first did, and the floor ended most paths after it. The mages' guild came out darker with depth (a mean of 23.29
levels at one bounce, 22.24 at two): a path ending at its first landing takes the cell's bright ambient there, and each
step further takes it through a delit albedo first, so a vanilla room lit by its ambient loses light.

## Memory

The landing's images exist above one ray only: at one ray the frame holds 5,248 bytes more than before the rays (its
images a texel wide); the pictures' chain, which traces one ray at every count, holds its landing a texel wide too.
