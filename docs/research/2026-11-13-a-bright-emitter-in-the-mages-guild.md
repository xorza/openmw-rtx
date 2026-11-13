# A bright emitter in the Balmora Mages Guild — 2026-11-13

A high-end NVIDIA laptop GPU, release builds, 2560×1440 at the quality upscaling mode, M[FR]'s profile. The place is
the Balmora Guild of Mages, standing at 744.9, -667.8, -606.6 and looking at 1280.1, -1110.7, -1325.9, 12:27, with a
test emitter the harness placed for these measurements at 798.5, -712, -678.5: a static ball eight units across,
about a hundred units in front of the eye and a few above the floor, beside a wall. Its emission is an
`osg::Material`'s, so its linear radiance is `EMISSIVE_INTENSITY` times the decoded colour.

A glow has no lamp of its own (`EMISSIVE_INTENSITY`): the ball lights the room only through the bounce rays that land
on it (`landingLightOf`).

## `noise` at the place, by the ball's decoded emission

| emission, decoded | as content | frame noise, mean / p99 | 16 averaged, mean / p99 | bias, frame / 16 averaged | fireflies, a thousand |
|---|---|---|---|---|---|
| 1 | a vanilla material's emissive colour at full | 1.05 / 4.50 | 6.96 / 127.44 | 4.87 / 5.68 | 0.03 |
| 10 | | 4.05 / 19.28 | 9.26 / 229.91 | 13.06 / 21.52 | 52.86 |
| 100 | | 13.07 / 56.37 | 11.32 / 206.37 | 18.44 / 40.98 | 216.97 |

At 100 the denoised frame is noisier than its 16 frames averaged, and two frames after a cut (`--cut=2`) its noise
is 30.11 / 117.12, its bias 44.43 and 480.18 pixels a thousand are fireflies. The view from beside the ball, with the ball
out of the frame, is clean at 100: 0.66 / 1.47, 0.03 fireflies a thousand.

The frame at 100 shows blotches 10 to 30 pixels across over the whole wall and floor the ball lights, where the mean
of 32 frames is smooth. The converged reference is itself visibly grainy there: the light a small source carries by
the bounce alone takes far more frames than the reference averages.

So a vanilla material's full glow does not make fireflies here; ten times it does, at one pixel in
nineteen.

## The denoiser's firefly hold

`noise --versus=antifirefly=1`, the same draws on both sides. The hold is ReBLUR's anti-firefly
(`ringHeldLuminance`): a slow mean held under the mean of the fast means in the 9×9 ring around it, less the 3×3 at
its centre, plus two of their deviations. As built it holds a history of `ACCUMULATE_RING_FRAMES` (8) frames or fewer;
"every history" is a build with that bound raised past any history, as ReBLUR holds every pixel.

At the ball, decoded emission 100:

| | frame noise, mean / p99 | bias, frame | fireflies, a thousand |
|---|---|---|---|
| no hold | 13.07 / 56.37 | 18.44 | 216.97 |
| hold, 8 frames or fewer | 13.06 / 56.25 | 18.43 | 216.74 |
| hold, every history | 11.85 / 60.75 | 37.97 | 12.40 |

The held-short history leaves the ball's fireflies where they are: they arrive in settled histories. Holding every
history takes 94% of them, and doubles the frame's bias against the reference: what it holds down is the ball's own
light, which reaches the wall rarely and brightly.

The `bounce` suite, vanilla content and no emitter, hold on every history against none:

| place | frame noise mean, none / every | bias, none / every | fireflies a thousand, none / every |
|---|---|---|---|
| balmora-mages-guild | 0.72 / 0.71 | 0.82 / 0.89 | 0.40 / 0.40 |
| probe-guild-planter | 0.67 / 0.67 | 0.63 / 0.68 | 0.20 / 0.19 |
| ahemmusa-yurt | 0.77 / 0.75 | 1.44 / 1.52 | 0.48 / 0.48 |
| akulakhan-chamber | 0.73 / 0.71 | 1.53 / 2.12 | 0.07 / 0.06 |
| seyda-neen-pier | 0.69 / 0.69 | 1.19 / 1.22 | 0.02 / 0.02 |
| seyda-neen-pond | 0.74 / 0.74 | 1.59 / 1.64 | 0.02 / 0.02 |

Where nothing fireflies the hold takes almost nothing; in the chamber lit by what glows in it, it takes 0.59 levels
of bias, as `ACCUMULATE_RING_FRAMES` records of the held-short hold.
